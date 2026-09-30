/*
 * system.cpp - Firmware-wide services: I2C bus lock, memory helpers, PSRAM task stacks
 * and the UI loop's wake-up signal.
 */
#include "system.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <freertos/idf_additions.h>
#include "mbedtls/platform.h"

static SemaphoreHandle_t i2c_mutex;
static SemaphoreHandle_t ui_wake_sem;  // binary: "something for the UI loop happened"
static system_message_fn_t message_handler;
static system_shutdown_fn_t shutdown_handler;

/* ================================ Memory ========================================== */

void *psram_malloc(size_t size) {
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void *psram_calloc(size_t count, size_t size) {
  void *p = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void psram_free(void *p) {
  heap_caps_free(p);
}

// TLS buffers of HTTPS downloads (~40 kB per connection) would otherwise come from
// internal RAM, which the Wi-Fi and Bluetooth stacks need.
static void *tls_calloc(size_t count, size_t size) {
  return psram_calloc(count, size);
}

static void tls_free(void *p) {
  heap_caps_free(p);
}

void mem_log(const char *what) {
  Serial.printf("[MEM] %s: internal free %u kB (largest block %u kB), PSRAM free %u kB\n", what,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

// A radio stack that runs out of internal RAM while starting aborts (restarts the watch),
// so they check first.
bool mem_internal_ok(uint32_t need_kb, const char *what) {
  mem_log(what);
  uint32_t free_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024;
  if (free_kb >= need_kb) return true;
  Serial.printf("[MEM] %s needs ~%u kB: not started\n", what, (unsigned)need_kb);
  return false;
}

bool task_create_psram(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                       UBaseType_t priority, TaskHandle_t *handle, BaseType_t core) {
  if (xTaskCreatePinnedToCoreWithCaps(fn, name, stack_bytes, arg, priority, handle, core,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
    return true;
  }
  // No PSRAM left: an internal stack still beats not running at all.
  return xTaskCreatePinnedToCore(fn, name, stack_bytes, arg, priority, handle, core) == pdPASS;
}

/* ================================ I2C bus ========================================= */

void i2c_lock() {
  if (i2c_mutex) xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
}

void i2c_unlock() {
  if (i2c_mutex) xSemaphoreGiveRecursive(i2c_mutex);
}

/* ================================ UI loop wake-up ================================= */

// A semaphore rather than a task notification: libraries that block the UI task on its
// own notification (BLE, some drivers) must never be woken by ours.
void system_ui_wait(uint32_t max_ms) {
  if (ui_wake_sem) xSemaphoreTake(ui_wake_sem, pdMS_TO_TICKS(max_ms ? max_ms : 1));
  else vTaskDelay(pdMS_TO_TICKS(max_ms ? max_ms : 1));
}

void system_ui_wake() {
  if (ui_wake_sem) xSemaphoreGive(ui_wake_sem);
}

void IRAM_ATTR system_ui_wake_from_isr() {
  if (!ui_wake_sem) return;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(ui_wake_sem, &woken);
  if (woken) portYIELD_FROM_ISR();
}

/* ================================ Messages ======================================== */

void system_set_message_handler(system_message_fn_t fn) {
  message_handler = fn;
}

void system_message(const char *text) {
  if (message_handler) message_handler(text);
  else Serial.printf("[MSG] %s\n", text);
}

/* ================================ Device facts ==================================== */

void system_info(SystemInfo *out) {
  out->chip = ESP.getChipModel();
  out->revision = ESP.getChipRevision();
  out->cores = ESP.getChipCores();
  out->cpu_mhz = ESP.getCpuFreqMHz();
  out->flash_bytes = ESP.getFlashChipSize();
  out->psram_bytes = ESP.getPsramSize();
  out->free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  out->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  esp_read_mac(out->mac_wifi, ESP_MAC_WIFI_STA);
  esp_read_mac(out->mac_bt, ESP_MAC_BT);
  out->idf_version = esp_get_idf_version();
  out->core_version = ESP_ARDUINO_VERSION_STR;
}

/* ================================ Restart and power off =========================== */

void system_set_shutdown_handler(system_shutdown_fn_t fn) {
  shutdown_handler = fn;
}

void system_restart() {
  if (shutdown_handler) shutdown_handler(true);
  ESP.restart();
}

void system_power_off() {
  if (shutdown_handler) shutdown_handler(false);
  esp_deep_sleep_start();  // only reached without a PMU that could cut the power
}

/* ================================ Init ============================================ */

void system_init() {
  Serial.begin(115200);
  mbedtls_platform_set_calloc_free(tls_calloc, tls_free);
  ui_wake_sem = xSemaphoreCreateBinary();
  i2c_mutex = xSemaphoreCreateRecursiveMutex();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
  Serial.printf("\n%s %s  (LVGL %d.%d.%d, esp32 core %s)\n", DEVICE_NAME, FW_VERSION, LVGL_VERSION_MAJOR,
                LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, ESP_ARDUINO_VERSION_STR);
}
