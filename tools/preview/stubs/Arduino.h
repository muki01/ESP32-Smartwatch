// Arduino.h for the PC preview: the small part of the Arduino/ESP32 API that the UI, the
// settings and the logic services use.
#pragma once
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <string>

#include "pv_time.h"

#define PI 3.14159265358979f
#define IRAM_ATTR
#define PROGMEM
#define ESP_ARDUINO_VERSION_STR "3.3.12"
#define BOARD_HAS_PSRAM 1
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_8BIT 8
#define LOW 0
#define HIGH 1
#define INPUT_PULLUP 2
#define OUTPUT 1

typedef void (*TaskFunction_t)(void *);
typedef unsigned int UBaseType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;

template <typename T, typename L, typename H>
static inline T constrain(T v, L lo, H hi) { return v < (T)lo ? (T)lo : v > (T)hi ? (T)hi : v; }
template <typename A, typename B>
static inline A min(A a, B b) { return a < (A)b ? a : (A)b; }
template <typename A, typename B>
static inline A max(A a, B b) { return a > (A)b ? a : (A)b; }

uint32_t millis();
#ifdef _WIN32
static inline struct tm *localtime_r(const time_t *t, struct tm *r) { localtime_s(r, t); return r; }
static inline struct tm *gmtime_r(const time_t *t, struct tm *r) { gmtime_s(r, t); return r; }
static inline int setenv(const char *name, const char *value, int) { return _putenv_s(name, value); }
#endif
static inline void delay(uint32_t) {}
static inline bool setCpuFrequencyMhz(uint32_t) { return true; }
static inline void pinMode(int, int) {}
static inline int digitalRead(int) { return HIGH; }
static inline void digitalWrite(int, int) {}
static inline uint32_t esp_random() { return 123456; }
static inline void *heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t) { return calloc(n, size); }
static inline void *heap_caps_realloc(void *p, size_t n, uint32_t) { return realloc(p, n); }
static inline void heap_caps_free(void *p) { free(p); }
static inline size_t heap_caps_get_free_size(uint32_t caps) { return caps & MALLOC_CAP_SPIRAM ? 7900000 : 183000; }
static inline const char *esp_get_idf_version() { return "v5.5.1"; }

static inline size_t strlcpy_host(char *d, const char *s, size_t n) {
  size_t l = strlen(s);
  if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; }
  return l;
}
#define strlcpy strlcpy_host
static inline size_t strlcat_host(char *d, const char *s, size_t n) {
  size_t used = strlen(d);
  if (used >= n) return used + strlen(s);
  return used + strlcpy_host(d + used, s, n - used);
}
#define strlcat strlcat_host

class String {
 public:
  String(const char *s = "") : s_(s ? s : "") {}
  const char *c_str() const { return s_.c_str(); }
  bool operator==(const char *o) const { return s_ == o; }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool isEmpty() const { return s_.empty(); }
  size_t length() const { return s_.size(); }
 private:
  std::string s_;
};

struct SerialStub {
  void begin(int) {}
  void println(const char *s) { puts(s); }
  void print(const char *s) { fputs(s, stdout); }
  template <typename... A> void printf(const char *f, A... a) { ::printf(f, a...); }
};
extern SerialStub Serial;

struct EspStub {
  void restart() {}
};
extern EspStub ESP;
