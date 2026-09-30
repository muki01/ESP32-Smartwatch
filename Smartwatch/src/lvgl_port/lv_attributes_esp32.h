/*
 * lv_attributes_esp32.h - LVGL attribute overrides for the ESP32-S3.
 * Included by LVGL through LV_ATTRIBUTE_CUSTOM_INCLUDE (lv_conf.h).
 */
#pragma once

#ifndef __ASSEMBLY__
#include "esp_attr.h"
#endif

/* LVGL's hottest drawing code (blenders, masks, colour mixing: ~20 kB) runs from IRAM and
 * is compiled with -O2, while the rest of the firmware keeps the core's -Os.
 * The esp32 core is built with a 16 kB instruction cache, far too small for the whole
 * render path: from flash these loops keep evicting each other. The IRAM comes out of
 * internal RAM, which is affordable since LVGL's heap and the TLS buffers live in PSRAM.
 * (GCC notes that the declaration's section wins over the definition's: harmless.) */
#define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR __attribute__((optimize("O2")))

/* Word-aligned image and font data lets the renderer use 32-bit copies. */
#define LV_ATTRIBUTE_MEM_ALIGN __attribute__((aligned(4)))
