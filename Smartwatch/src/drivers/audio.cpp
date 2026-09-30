/*
 * audio.cpp - ES8311 codec + I2S: UI sounds, notification chime, alert sounds, the WAV
 * music player, the voice recorder and the microphone listener (clap control).
 *
 * A single audio task owns the I2S bus. The UI talks to it through a queue, so a click
 * or a track change never blocks the UI loop. The task publishes its state in plain
 * variables; an LVGL timer turns changes into subj_music / subj_recorder.
 * The speaker amplifier is powered only while something is actually playing.
 *
 * Alerts (alarm, timer, incoming call, find my watch) loop a pattern until stopped,
 * pause the music and play at 75 % volume or more. Silent mode mutes clicks and
 * notification chimes but never alarms.
 * With clap control on, the idle task reads the microphone at 16 kHz and runs the clap
 * detector (algorithms/clap_detector.h); anything else that needs the bus goes first.
 * Buffers and the task stack live in PSRAM.
 */
#include "audio.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <SD_MMC.h>
#include "../algorithms/clap_detector.h"
#include "../core/settings.h"
#include "../core/system.h"
#include "es8311/es8311.h"
#include "sd_card.h"

#define AUDIO_RATE          48000
#define AUDIO_MIC_GAIN      (es8311_mic_gain_t)(7)
#define AUDIO_TASK_STACK    6144
#define AUDIO_CHUNK         4096
#define AUDIO_AMP_WARMUP_MS 15
#define AUDIO_TONE_BUF      2400    // samples per synthesis chunk (50 ms at 48 kHz)
#define AUDIO_ALERT_MIN_VOL 75
#define MUSIC_MAX_FILES     100
#define MUSIC_PATH_LEN      128
#define REC_RATE            16000   // voice recordings and the clap listener: 16 kHz mono 16-bit
#define REC_MAX_S           3600
#define LISTEN_FRAMES       128     // 8 ms blocks for the clap detector
#define LISTEN_SETTLE_MS    150     // after any sound or reconfiguration: ignore the microphone

enum AudioCmdType : uint8_t {
  AUDIO_CMD_BEEP, AUDIO_CMD_TOGGLE, AUDIO_CMD_NEXT, AUDIO_CMD_PREV, AUDIO_CMD_PLAY,
  AUDIO_CMD_NOTIFY, AUDIO_CMD_ALERT_START, AUDIO_CMD_ALERT_STOP,
  AUDIO_CMD_REC_START, AUDIO_CMD_REC_STOP, AUDIO_CMD_PLAY_FILE, AUDIO_CMD_PLAY_FILE_STOP
};

struct AudioCmd {
  AudioCmdType type;
  uint16_t freq;   // beep: Hz; play: track index; alert: AlertSound
  uint16_t ms;
};

struct WavInfo {
  uint32_t sample_rate;
  uint16_t channels;
  uint16_t bits;
  uint32_t byte_rate;
  uint32_t data_size;
  uint32_t data_start;
  uint32_t seconds;
};

struct AlertStep {
  uint16_t freq;  // 0 = pause
  uint16_t ms;
};

static const AlertStep ALERT_ALARM_STEPS[] = { { 880, 110 }, { 0, 70 }, { 880, 110 }, { 0, 70 }, { 880, 110 }, { 0, 70 }, { 880, 110 }, { 0, 650 } };
static const AlertStep ALERT_TIMER_STEPS[] = { { 1760, 80 }, { 0, 60 }, { 1760, 80 }, { 0, 60 }, { 1760, 80 }, { 0, 800 } };
static const AlertStep ALERT_FIND_STEPS[] = { { 1047, 140 }, { 1319, 140 }, { 1568, 140 }, { 2093, 260 }, { 0, 500 } };
static const AlertStep ALERT_CALL_STEPS[] = { { 1320, 90 }, { 1100, 90 }, { 1320, 90 }, { 1100, 90 }, { 1320, 90 }, { 1100, 90 }, { 0, 1300 } };

static I2SClass i2s;
static es8311_handle_t es_handle;
static QueueHandle_t audio_queue;
static uint32_t audio_rate = AUDIO_RATE;
static int16_t *tone_buf;                        // PSRAM
static uint8_t *stream_buf;                      // PSRAM
static volatile int32_t audio_user_volume = 70;  // the Volume setting, mirrored for the audio task

// Alert state, owned by the audio task.
static volatile bool audio_alert_on;
static uint8_t audio_alert_kind;
static uint8_t audio_alert_pos;

// Music player.
static char (*music_files)[MUSIC_PATH_LEN];      // PSRAM, MUSIC_MAX_FILES entries
static int music_count;
static File music_file;
static WavInfo music_info;
static uint32_t music_bytes;
static volatile bool music_playing;              // a track is open
static volatile bool music_paused;
static volatile bool music_repeat_one;
static volatile int music_index = -1;
static volatile uint32_t music_pos_s;
static volatile uint32_t music_len_s;
static volatile uint32_t music_version;
static uint32_t music_version_seen;

// Voice recorder.
static char audio_path[MUSIC_PATH_LEN];          // file for the next record/play command
static File rec_file;
static volatile bool rec_active;                 // recording
static volatile bool rec_play_active;            // playing a recording
static volatile uint32_t rec_bytes;              // recorded or played so far
static volatile uint16_t rec_level;              // loudness of the last chunk, 0..100

// Microphone listener.
static ClapDetector clap_detector;
static volatile bool listen_want;
static volatile bool clap_heard;
static bool listening;                           // RX configured for the listener (audio task)
static uint32_t listen_from_ms;                  // ignore the microphone until then

/* ================================ Codec / amplifier =============================== */

static bool audio_codec_init() {
  I2CGuard lock;
  es_handle = es8311_create(0, ES8311_ADDRRES_0);
  if (!es_handle) return false;
  const es8311_clock_config_t clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = AUDIO_RATE * 256,
    .sample_frequency = AUDIO_RATE
  };
  if (es8311_init(es_handle, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_sample_frequency_config(es_handle, clk.mclk_frequency, clk.sample_frequency);
  es8311_microphone_config(es_handle, false);
  es8311_microphone_gain_set(es_handle, AUDIO_MIC_GAIN);
  return true;
}

static void audio_amp(bool on) {
  digitalWrite(SPEAKER_AMP_PIN, on ? HIGH : LOW);
  if (on) vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_WARMUP_MS));
}

static void audio_set_rate(uint32_t rate) {
  if (rate == audio_rate) return;
  i2s.configureTX(rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH);
  if (es_handle) {
    I2CGuard lock;
    es8311_sample_frequency_config(es_handle, rate * 256, rate);
  }
  audio_rate = rate;
  listening = false;  // the microphone clock changed with it
}

// 0 % mutes; 1..100 % map onto the codec range that was tuned for this speaker.
static void audio_codec_volume(int32_t percent) {
  if (!es_handle) return;
  I2CGuard lock;
  es8311_voice_volume_set(es_handle, percent <= 0 ? 0 : map(percent, 1, 100, 56, 82), NULL);
}

static void audio_volume_obs(lv_observer_t *observer, lv_subject_t *subject) {
  audio_user_volume = lv_subject_get_int(subject);
  if (!audio_alert_on) audio_codec_volume(audio_user_volume);
}

/* ================================ Tones =========================================== */

// Sine tone with 2.5 ms fades (no clicks). The amplifier must already be on.
static void audio_tone(uint16_t freq, uint16_t ms, float amplitude) {
  size_t total = (size_t)audio_rate * ms / 1000;
  size_t fade = audio_rate / 400;
  if (fade * 2 > total) fade = total / 2;
  float step = 2.0f * PI * freq / audio_rate;
  for (size_t done = 0; done < total;) {
    size_t n = total - done < AUDIO_TONE_BUF ? total - done : AUDIO_TONE_BUF;
    for (size_t i = 0; i < n; i++) {
      size_t k = done + i;
      float env = 1.0f;
      if (k < fade) env = (float)k / fade;
      else if (k >= total - fade) env = (float)(total - k) / fade;
      tone_buf[i] = (int16_t)(sinf(step * k) * amplitude * env);
    }
    i2s.write((const uint8_t *)tone_buf, n * sizeof(int16_t));
    done += n;
  }
}

static void audio_play_beep(uint16_t freq, uint16_t ms) {
  if (ms > 200) ms = 200;
  audio_amp(true);
  audio_tone(freq, ms, 9000.0f);
  vTaskDelay(pdMS_TO_TICKS(40));  // the DMA buffers (30 ms) drain before the amp goes off
  audio_amp(false);
}

// Two soft rising notes (C6, G6).
static void audio_play_chime() {
  audio_amp(true);
  audio_tone(1047, 90, 7000.0f);
  audio_tone(1568, 170, 7000.0f);
  vTaskDelay(pdMS_TO_TICKS(40));
  audio_amp(false);
}

static void audio_alert_step() {
  const AlertStep *steps;
  size_t count;
  switch (audio_alert_kind) {
    case ALERT_TIMER: steps = ALERT_TIMER_STEPS; count = sizeof(ALERT_TIMER_STEPS) / sizeof(AlertStep); break;
    case ALERT_FIND:  steps = ALERT_FIND_STEPS;  count = sizeof(ALERT_FIND_STEPS) / sizeof(AlertStep); break;
    case ALERT_CALL:  steps = ALERT_CALL_STEPS;  count = sizeof(ALERT_CALL_STEPS) / sizeof(AlertStep); break;
    default:          steps = ALERT_ALARM_STEPS; count = sizeof(ALERT_ALARM_STEPS) / sizeof(AlertStep); break;
  }
  const AlertStep &s = steps[audio_alert_pos % count];
  audio_alert_pos = (audio_alert_pos + 1) % count;
  if (s.freq) {
    audio_tone(s.freq, s.ms, 11000.0f);
  } else {
    AudioCmd cmd;
    xQueuePeek(audio_queue, &cmd, pdMS_TO_TICKS(s.ms));  // a command ends the pause early
  }
}

/* ================================ Music player ==================================== */

static uint32_t le32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool wav_read_info(const char *path, WavInfo &info) {
  File f = SD_MMC.open(path);
  if (!f) return false;
  memset(&info, 0, sizeof(info));
  uint8_t hdr[12];
  bool ok = f.read(hdr, 12) == 12 && !memcmp(hdr, "RIFF", 4) && !memcmp(hdr + 8, "WAVE", 4);
  uint16_t format = 0;
  while (ok && f.available()) {
    uint8_t chunk[8];
    if (f.read(chunk, 8) != 8) break;
    uint32_t size = le32(chunk + 4);
    if (!memcmp(chunk, "fmt ", 4)) {
      uint8_t fmt[16];
      if (size < 16 || f.read(fmt, 16) != 16) break;
      format = fmt[0] | (fmt[1] << 8);
      info.channels = fmt[2] | (fmt[3] << 8);
      info.sample_rate = le32(fmt + 4);
      info.byte_rate = le32(fmt + 8);
      info.bits = fmt[14] | (fmt[15] << 8);
      f.seek(f.position() + size - 16 + (size & 1));
    } else if (!memcmp(chunk, "data", 4)) {
      info.data_size = size;
      info.data_start = f.position();
      break;
    } else {
      f.seek(f.position() + size + (size & 1));
    }
  }
  f.close();
  info.seconds = info.byte_rate ? info.data_size / info.byte_rate : 0;
  return ok && format == 1 && info.bits == 16 && (info.channels == 1 || info.channels == 2) && info.data_start &&
         info.data_size;
}

static void music_scan_dir(const char *dir) {
  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) return;
  for (File entry = root.openNextFile(); entry && music_count < MUSIC_MAX_FILES; entry = root.openNextFile()) {
    const char *name = entry.name();
    size_t len = strlen(name);
    if (!entry.isDirectory() && len > 4 && name[0] != '.' && !strcasecmp(name + len - 4, ".wav")) {
      snprintf(music_files[music_count++], MUSIC_PATH_LEN, "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", name);
    }
    entry.close();
  }
  root.close();
}

// Alphabetical, so the library list and "next" follow the file names.
static void music_sort() {
  for (int i = 1; i < music_count; i++) {
    char key[MUSIC_PATH_LEN];
    memcpy(key, music_files[i], MUSIC_PATH_LEN);
    int j = i - 1;
    for (; j >= 0 && strcasecmp(music_files[j], key) > 0; j--) memcpy(music_files[j + 1], music_files[j], MUSIC_PATH_LEN);
    memcpy(music_files[j + 1], key, MUSIC_PATH_LEN);
  }
}

static void music_scan() {
  music_count = 0;
  if (!sd_available() || !music_files) return;
  music_scan_dir("/");
  music_scan_dir("/music");
  music_scan_dir("/musics");
  music_sort();
  Serial.printf("[AUDIO] %d music file(s)\n", music_count);
}

static void music_close() {
  if (music_file) music_file.close();
  if (music_playing) audio_amp(false);
  music_playing = false;
  music_paused = false;
  music_version = music_version + 1;
}

static bool music_open_track(int index) {
  if (music_file) music_file.close();
  if (index < 0) index = 0;
  for (int tries = 0; tries < music_count; tries++, index = (index + 1) % music_count) {
    if (!wav_read_info(music_files[index], music_info)) {
      Serial.printf("[AUDIO] skipped (needs 16-bit PCM WAV): %s\n", music_files[index]);
      continue;
    }
    music_file = SD_MMC.open(music_files[index]);
    if (!music_file || !music_file.seek(music_info.data_start)) continue;
    audio_set_rate(music_info.sample_rate);
    if (!music_playing || music_paused) audio_amp(true);
    music_index = index;
    music_bytes = 0;
    music_pos_s = 0;
    music_len_s = music_info.seconds;
    music_playing = true;
    music_paused = false;
    music_version = music_version + 1;
    return true;
  }
  music_close();
  return false;
}

/* ================================ Voice recorder ================================== */

static void rec_wav_header(uint8_t *h, uint32_t data_bytes) {
  const uint32_t rate = REC_RATE, byte_rate = REC_RATE * 2, riff = 36 + data_bytes, fmt_size = 16;
  const uint16_t pcm = 1, channels = 1, align = 2, bits = 16;
  memcpy(h, "RIFF", 4);
  memcpy(h + 4, &riff, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  memcpy(h + 16, &fmt_size, 4);
  memcpy(h + 20, &pcm, 2);
  memcpy(h + 22, &channels, 2);
  memcpy(h + 24, &rate, 4);
  memcpy(h + 28, &byte_rate, 4);
  memcpy(h + 32, &align, 2);
  memcpy(h + 34, &bits, 2);
  memcpy(h + 36, "data", 4);
  memcpy(h + 40, &data_bytes, 4);
}

// Microphone on: both I2S slots at 16 kHz (the codec's ADC data sits in one of them,
// the other is silent or the same).
static void mic_open() {
  audio_set_rate(REC_RATE);
  i2s.configureRX(REC_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

// Reads one block of stereo frames and folds it to mono in place. Returns the frames read.
static size_t mic_read(size_t bytes) {
  size_t n = i2s.readBytes((char *)stream_buf, bytes);
  int16_t *s = (int16_t *)stream_buf;
  size_t frames = n / 4;
  for (size_t i = 0; i < frames; i++) s[i] = (int16_t)(((int32_t)s[2 * i] + s[2 * i + 1]) / 2);
  return frames;
}

static void rec_stop() {
  if (!rec_active) return;
  rec_active = false;
  uint8_t h[44];
  rec_wav_header(h, rec_bytes);
  rec_file.seek(0);
  rec_file.write(h, sizeof(h));
  rec_file.close();
  Serial.printf("[AUDIO] recorded %u s: %s\n", (unsigned)(rec_bytes / (REC_RATE * 2)), audio_path);
}

static void rec_start() {
  if (music_playing) music_close();
  rec_play_active = false;
  rec_file = SD_MMC.open(audio_path, FILE_WRITE);
  if (!rec_file) {
    Serial.printf("[AUDIO] cannot create %s\n", audio_path);
    return;
  }
  uint8_t h[44];
  rec_wav_header(h, 0);  // sizes are filled in at the end
  rec_file.write(h, sizeof(h));
  mic_open();
  rec_bytes = 0;
  rec_level = 0;
  rec_active = true;
}

// One chunk from the microphone to the file.
static void rec_capture_chunk() {
  size_t frames = mic_read(AUDIO_CHUNK);
  int16_t *s = (int16_t *)stream_buf;
  int32_t peak = 0;
  for (size_t i = 0; i < frames; i++) peak = max(peak, (int32_t)abs(s[i]));
  if (frames && rec_file.write(stream_buf, frames * 2) != frames * 2) {  // card full or removed
    rec_stop();
    return;
  }
  rec_bytes = rec_bytes + frames * 2;
  rec_level = (uint16_t)min((int32_t)100, peak * 100 / 12000);
  if (rec_bytes >= (uint32_t)REC_MAX_S * REC_RATE * 2) rec_stop();
}

static void rec_play_stop() {
  if (!rec_play_active) return;
  rec_play_active = false;
  if (music_file) music_file.close();
  vTaskDelay(pdMS_TO_TICKS(40));
  audio_amp(false);
}

static void rec_play_start() {
  rec_play_stop();
  if (music_playing) music_close();
  if (!wav_read_info(audio_path, music_info)) return;
  music_file = SD_MMC.open(audio_path);
  if (!music_file || !music_file.seek(music_info.data_start)) return;
  audio_set_rate(music_info.sample_rate);
  audio_amp(true);
  rec_bytes = 0;
  rec_play_active = true;
}

/* ================================ Clap listener =================================== */

static void listen_block() {
  if (!listening) {
    mic_open();
    clap_detector.reset();
    listening = true;
    listen_from_ms = millis() + LISTEN_SETTLE_MS;
  }
  size_t frames = mic_read(LISTEN_FRAMES * 4);
  uint32_t now = millis();
  if ((int32_t)(now - listen_from_ms) < 0) return;
  if (clap_detector.feed((const int16_t *)stream_buf, frames, now)) {
    clap_heard = true;
    system_ui_wake();
  }
}

/* ================================ Audio task ====================================== */

static void audio_handle(const AudioCmd &cmd) {
  listening = false;  // whatever happens now, the microphone settles again afterwards
  switch (cmd.type) {
    case AUDIO_CMD_REC_START:
      if (!audio_alert_on && !rec_active) rec_start();
      break;
    case AUDIO_CMD_REC_STOP:
      rec_stop();
      break;
    case AUDIO_CMD_PLAY_FILE:
      if (!audio_alert_on && !rec_active) rec_play_start();
      break;
    case AUDIO_CMD_PLAY_FILE_STOP:
      rec_play_stop();
      break;
    case AUDIO_CMD_BEEP:
      if (!audio_alert_on && !rec_active && !rec_play_active && (!music_playing || music_paused)) audio_play_beep(cmd.freq, cmd.ms);
      break;
    case AUDIO_CMD_NOTIFY:
      if (!audio_alert_on && !rec_active && !rec_play_active && (!music_playing || music_paused)) audio_play_chime();
      break;
    case AUDIO_CMD_TOGGLE:
      if (audio_alert_on || rec_active) break;
      rec_play_stop();
      if (!music_playing) {
        if (music_count) music_open_track(music_index);
      } else {
        music_paused = !music_paused;
        if (!music_paused) audio_set_rate(music_info.sample_rate);  // an alert or the listener may have switched it
        audio_amp(!music_paused);
        music_version = music_version + 1;
      }
      break;
    case AUDIO_CMD_PLAY:
      if (music_count && !audio_alert_on && cmd.freq < music_count) music_open_track(cmd.freq);
      break;
    case AUDIO_CMD_NEXT:
      if (music_count && !audio_alert_on) music_open_track((music_index + 1) % music_count);
      break;
    case AUDIO_CMD_PREV:
      // Like every player: first press restarts the track, a second one goes back.
      if (music_count && !audio_alert_on) {
        int prev = music_index <= 0 ? music_count - 1 : music_index - 1;
        music_open_track(music_playing && music_pos_s >= 3 ? music_index : prev);
      }
      break;
    case AUDIO_CMD_ALERT_START:
      rec_stop();  // an alarm ends a recording (it is kept) or the playback of one
      if (rec_play_active) {
        rec_play_active = false;
        if (music_file) music_file.close();
      }
      if (music_playing && !music_paused) {
        music_paused = true;  // stays paused afterwards: the user resumes it
        music_version = music_version + 1;
      } else if (!audio_alert_on) {
        audio_amp(true);
      }
      audio_set_rate(AUDIO_RATE);
      audio_alert_kind = (uint8_t)cmd.freq;
      audio_alert_pos = 0;
      audio_alert_on = true;
      audio_codec_volume(max((int32_t)audio_user_volume, (int32_t)AUDIO_ALERT_MIN_VOL));
      break;
    case AUDIO_CMD_ALERT_STOP:
      if (!audio_alert_on) break;
      audio_alert_on = false;
      vTaskDelay(pdMS_TO_TICKS(40));
      audio_amp(false);
      audio_codec_volume(audio_user_volume);
      break;
  }
}

static void audio_task(void *arg) {
  for (;;) {
    bool streaming = (music_playing && !music_paused) || rec_play_active;
    bool busy = audio_alert_on || streaming || rec_active;
    bool listen = !busy && listen_want;
    AudioCmd cmd;
    if (xQueueReceive(audio_queue, &cmd, busy || listen ? 0 : portMAX_DELAY) == pdTRUE) {
      audio_handle(cmd);
      continue;
    }
    if (audio_alert_on) {
      audio_alert_step();
      continue;
    }
    if (rec_active) {
      rec_capture_chunk();
      continue;
    }
    if (listen) {
      listen_block();
      continue;
    }

    int n = music_file.read(stream_buf, AUDIO_CHUNK);
    if (n <= 0) {  // end of track
      if (rec_play_active) rec_play_stop();
      else if (!music_open_track(music_repeat_one ? music_index : (music_index + 1) % music_count)) music_close();
      continue;
    }
    if (rec_play_active) rec_bytes = rec_bytes + n;
    else music_bytes += n;
    size_t out = n;
    if (music_info.channels == 2) {  // the speaker is mono: mix L+R
      int16_t *s = (int16_t *)stream_buf;
      size_t frames = n / 4;
      for (size_t i = 0; i < frames; i++) s[i] = (int16_t)(((int32_t)s[2 * i] + s[2 * i + 1]) / 2);
      out = frames * 2;
    }
    i2s.write(stream_buf, out);  // blocks until the DMA has room: this paces the stream
    if (music_info.byte_rate && !rec_play_active) music_pos_s = music_bytes / music_info.byte_rate;
  }
}

// Track or play state changed in the audio task: tell the UI.
static void music_watch_cb(lv_timer_t *t) {
  subj_set(subj_recorder, rec_active ? REC_RECORDING : rec_play_active ? REC_PLAYING : REC_IDLE);
  uint32_t v = music_version;
  if (v == music_version_seen) return;
  music_version_seen = v;
  subj_bump(subj_music);
}

/* ================================ Public API ====================================== */

void audio_init() {
  pinMode(SPEAKER_AMP_PIN, OUTPUT);
  digitalWrite(SPEAKER_AMP_PIN, LOW);  // amplifier off until something plays

  tone_buf = (int16_t *)psram_malloc(AUDIO_TONE_BUF * sizeof(int16_t));
  stream_buf = (uint8_t *)psram_malloc(AUDIO_CHUNK);
  music_files = (char(*)[MUSIC_PATH_LEN])psram_calloc(MUSIC_MAX_FILES, MUSIC_PATH_LEN);
  if (!tone_buf || !stream_buf || !music_files) {
    Serial.println("[AUDIO] no memory for audio buffers");
    return;
  }

  i2s.setPins(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
  if (!i2s.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH)) {
    Serial.println("[AUDIO] I2S init failed");
    return;
  }
  if (!audio_codec_init()) Serial.println("[AUDIO] ES8311 init failed");
  music_scan();

  audio_queue = xQueueCreate(8, sizeof(AudioCmd));
  task_create_psram(audio_task, "audio", AUDIO_TASK_STACK, NULL, 5, NULL, tskNO_AFFINITY);
  lv_subject_add_observer(subj_volume, audio_volume_obs, NULL);
  lv_timer_create(music_watch_cb, 200, NULL);
}

static void audio_send(AudioCmdType type, uint16_t freq, uint16_t ms) {
  if (!audio_queue) return;
  AudioCmd cmd = { type, freq, ms };
  xQueueSend(audio_queue, &cmd, 0);  // never block the UI; drop if the queue is full
}

void audio_beep(uint16_t freq_hz, uint16_t duration_ms) {
  audio_send(AUDIO_CMD_BEEP, freq_hz, duration_ms);
}

// Short tick for touch feedback, only when "Touch sounds" is on and silent mode is off.
void audio_click() {
  if (lv_subject_get_int(subj_touch_sounds) && !lv_subject_get_int(subj_silent)) audio_send(AUDIO_CMD_BEEP, 2200, 18);
}

void audio_notify() {
  if (!lv_subject_get_int(subj_notif_sounds) || lv_subject_get_int(subj_silent) || lv_subject_get_int(subj_dnd)) return;
  audio_send(AUDIO_CMD_NOTIFY, 0, 0);
}

void audio_alert_start(AlertSound kind) {
  audio_send(AUDIO_CMD_ALERT_START, kind, 0);
}

void audio_alert_stop() {
  audio_send(AUDIO_CMD_ALERT_STOP, 0, 0);
}

void music_get_state(MusicState *out) {
  out->active = music_playing;
  out->playing = music_playing && !music_paused;
  out->repeat_one = music_repeat_one;
  out->index = music_index;
  out->count = music_count;
  out->pos_s = music_pos_s;
  out->len_s = music_len_s;
}

// "Artist - Title.wav" -> artist, title. Without " - " the artist is empty.
void music_track_info(int index, char *artist, size_t artist_len, char *title, size_t title_len) {
  artist[0] = 0;
  title[0] = 0;
  if (index < 0 || index >= music_count) return;
  const char *path = music_files[index];
  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  char buf[MUSIC_PATH_LEN];
  strlcpy(buf, name, sizeof(buf));
  char *ext = strrchr(buf, '.');
  if (ext) *ext = 0;
  char *dash = strstr(buf, " - ");
  if (dash) {
    *dash = 0;
    strlcpy(artist, buf, artist_len);
    strlcpy(title, dash + 3, title_len);
  } else {
    strlcpy(title, buf, title_len);
  }
}

void music_toggle() {
  audio_send(AUDIO_CMD_TOGGLE, 0, 0);
}

void music_next() {
  audio_send(AUDIO_CMD_NEXT, 0, 0);
}

void music_prev() {
  audio_send(AUDIO_CMD_PREV, 0, 0);
}

void music_play(int index) {
  if (index >= 0) audio_send(AUDIO_CMD_PLAY, (uint16_t)index, 0);
}

void music_set_repeat(bool one) {
  music_repeat_one = one;
  music_version = music_version + 1;
}

static void audio_send_path(AudioCmdType type, const char *path) {
  if (!audio_queue) return;
  strlcpy(audio_path, path, sizeof(audio_path));
  audio_send(type, 0, 0);
}

bool recorder_start(const char *path) {
  if (!sd_available() || !audio_queue || rec_active) return false;
  audio_send_path(AUDIO_CMD_REC_START, path);
  return true;
}

void recorder_stop() {
  audio_send(AUDIO_CMD_REC_STOP, 0, 0);
}

bool recorder_play(const char *path) {
  if (!sd_available() || !audio_queue || rec_active) return false;
  audio_send_path(AUDIO_CMD_PLAY_FILE, path);
  return true;
}

void recorder_play_stop() {
  audio_send(AUDIO_CMD_PLAY_FILE_STOP, 0, 0);
}

uint32_t recorder_seconds() {
  uint32_t rate = rec_play_active && music_info.byte_rate ? music_info.byte_rate : REC_RATE * 2;
  return rec_bytes / rate;
}

uint32_t recorder_level() {
  return rec_active ? rec_level : 0;
}

void audio_listen(bool on) {
  if (listen_want == on) return;
  listen_want = on;
  if (on && audio_queue) {  // wake the idle task so it starts listening
    AudioCmd cmd = { AUDIO_CMD_REC_STOP, 0, 0 };
    xQueueSend(audio_queue, &cmd, 0);
  }
  Serial.printf("[AUDIO] clap listener %s\n", on ? "on" : "off");
}

bool audio_take_clap() {
  if (!clap_heard) return false;
  clap_heard = false;
  return true;
}
