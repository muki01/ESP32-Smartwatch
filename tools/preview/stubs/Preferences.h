#pragma once
#include "Arduino.h"

// Preview: an in-memory NVS shared by every Preferences object (stubs.cpp).
class Preferences {
 public:
  bool begin(const char *ns, bool read_only = false);
  void end() {}
  int32_t getInt(const char *key, int32_t def = 0);
  size_t putInt(const char *key, int32_t v);
  uint32_t getUInt(const char *key, uint32_t def = 0);
  size_t putUInt(const char *key, uint32_t v);
  uint16_t getUShort(const char *key, uint16_t def = 0);
  size_t putUShort(const char *key, uint16_t v);
  uint8_t getUChar(const char *key, uint8_t def = 0);
  size_t putUChar(const char *key, uint8_t v);
  bool getBool(const char *key, bool def = false);
  size_t putBool(const char *key, bool v);
  size_t getBytes(const char *key, void *buf, size_t len);
  size_t putBytes(const char *key, const void *buf, size_t len);
  size_t getBytesLength(const char *key);
  size_t getString(const char *key, char *buf, size_t len);
  String getString(const char *key, const String def = String());
  size_t putString(const char *key, const char *v);
  bool isKey(const char *key);
  bool clear();
  bool remove(const char *key);

 private:
  std::string ns_;
};
