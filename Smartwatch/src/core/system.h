/*
 * system.h - Firmware identity, build checks and the system services every module uses:
 * the shared I2C bus lock, memory helpers, tasks with PSRAM stacks and the UI loop's
 * wake-up signal.
 *
 * Threading model
 *   LVGL runs in the Arduino loop task only. Drivers and services that work in their own
 *   tasks (audio, motion sensor, network jobs, radio stacks) never call LVGL: they publish
 *   plain state and wake the loop (system_ui_wake()), which applies it.
 */
#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "board.h"

#if !defined(MUKI_WATCH_LV_CONF)
#error "LVGL does not use this sketch's lv_conf.h. Use the esp32 core 3.x and delete any other lv_conf.h next to the lvgl library folder."
#endif
#if LVGL_VERSION_MAJOR != 9
#error "This firmware needs LVGL 9 (tested with 9.6.0)."
#endif
#if !defined(BOARD_HAS_PSRAM)
#error "Enable PSRAM (Tools > PSRAM > Enabled): the UI, network and audio buffers live in the 8 MB PSRAM."
#endif

#define FW_VERSION   "3.0.0"
#define DEVICE_NAME  "Muki Watch"
#define DEVICE_MODEL "ESP32-S3-Touch-AMOLED-1.8"

// Serial, TLS allocator, I2C bus and its lock. First call of setup().
void system_init();

/* ---- Shared I2C bus ------------------------------------------------------------------
 * PMU, touch, RTC, motion sensor and codec share one bus. Arduino's Wire locks a transfer
 * but not the Wire.read() calls after it, so two tasks reading at once can get each
 * other's bytes. Every I2C access holds this (recursive) lock for the whole transaction:
 * `I2CGuard lock;` at the top of the block. */
void i2c_lock();
void i2c_unlock();

struct I2CGuard {
  I2CGuard() { i2c_lock(); }
  ~I2CGuard() { i2c_unlock(); }
  I2CGuard(const I2CGuard &) = delete;
  I2CGuard &operator=(const I2CGuard &) = delete;
};

/* ---- Memory -------------------------------------------------------------------------
 * Internal RAM is kept for the radios, DMA and task stacks; everything else goes to the
 * 8 MB PSRAM. */
void *psram_malloc(size_t size);            // PSRAM, internal RAM when PSRAM is full
void *psram_calloc(size_t count, size_t size);
void psram_free(void *p);
bool mem_internal_ok(uint32_t need_kb, const char *what);  // logs the free RAM; false below need_kb
void mem_log(const char *what);

// A task whose stack lives in PSRAM (never deleted). Not for code that writes the flash.
bool task_create_psram(TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                       UBaseType_t priority, TaskHandle_t *handle, BaseType_t core);

/* ---- UI loop wake-up ----------------------------------------------------------------
 * The loop sleeps until the next LVGL timer is due. Events from other tasks and ISRs
 * (touch, wrist raise, network results) wake it at once. */
void system_ui_wait(uint32_t max_ms);
void system_ui_wake();
void system_ui_wake_from_isr();  // IRAM: callable from interrupt handlers

/* ---- Short messages -----------------------------------------------------------------
 * Drivers and services report short results to the user ("Wi-Fi could not start")
 * without depending on the UI: the UI installs the handler (a toast). UI loop only. */
typedef void (*system_message_fn_t)(const char *text);
void system_set_message_handler(system_message_fn_t fn);
void system_message(const char *text);

/* ---- Device facts -------------------------------------------------------------------
 * Chip, memory and versions for Settings > About. */
struct SystemInfo {
  const char *chip;          // "ESP32-S3"
  int revision;
  int cores;
  uint32_t cpu_mhz;
  uint32_t flash_bytes;
  uint32_t psram_bytes;
  uint32_t free_internal;    // bytes free now
  uint32_t free_psram;
  uint8_t mac_wifi[6];
  uint8_t mac_bt[6];
  const char *idf_version;
  const char *core_version;  // Arduino core
};
void system_info(SystemInfo *out);

/* ---- Restart and power off ----------------------------------------------------------
 * The application installs the shutdown sequence (store pending data, screen off, then
 * restart or cut the power); any module can ask for it. */
typedef void (*system_shutdown_fn_t)(bool restart);
void system_set_shutdown_handler(system_shutdown_fn_t fn);
void system_restart();
void system_power_off();
