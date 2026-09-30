/*
 * psram_json.h - An ArduinoJson allocator that puts parsed documents in PSRAM: network
 * answers can take several kB, and internal RAM is kept for the radios.
 *
 *   JsonDocument doc(psram_json_allocator());
 */
#pragma once

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

struct PsramJsonAllocator : ArduinoJson::Allocator {
  void *allocate(size_t size) override {
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
  }
  void deallocate(void *ptr) override {
    heap_caps_free(ptr);
  }
  void *reallocate(void *ptr, size_t size) override {
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
  }
};

inline ArduinoJson::Allocator *psram_json_allocator() {
  static PsramJsonAllocator allocator;
  return &allocator;
}
