/*
 * lv_mem_esp32.c - LVGL heap in PSRAM (lv_conf.h: LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM).
 *
 * With the C library allocator every LVGL allocation below 4 kB (widgets, styles, label
 * texts, event lists: thousands of small blocks) would land in internal RAM, which leaves
 * too little for Wi-Fi and Bluetooth. This board has 8 MB of octal PSRAM, so LVGL uses that
 * and internal RAM stays free for the radios, DMA and task stacks. The display draw buffers
 * are allocated separately in internal DMA memory (src/drivers/display.cpp).
 * Kept in a C file: LVGL calls these functions with C linkage.
 */
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "esp_heap_caps.h"

#define LV_MEM_CAPS_PSRAM    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CAPS_ANY      (MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void * lv_malloc_core(size_t size)
{
    void * p = heap_caps_malloc(size, LV_MEM_CAPS_PSRAM);
    return p ? p : heap_caps_malloc(size, LV_MEM_CAPS_ANY);  /* no PSRAM left: internal RAM */
}

void * lv_realloc_core(void * p, size_t new_size)
{
    void * n = heap_caps_realloc(p, new_size, LV_MEM_CAPS_PSRAM);
    return n ? n : heap_caps_realloc(p, new_size, LV_MEM_CAPS_ANY);
}

void lv_free_core(void * p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, LV_MEM_CAPS_PSRAM);
    mon_p->total_size = heap_caps_get_total_size(LV_MEM_CAPS_PSRAM);
    mon_p->free_size = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->free_cnt = info.free_blocks;
    mon_p->used_cnt = info.allocated_blocks;
    mon_p->max_used = mon_p->total_size - info.minimum_free_bytes;
    mon_p->used_pct = mon_p->total_size ? (uint8_t)(100 - (100 * info.total_free_bytes) / mon_p->total_size) : 0;
    mon_p->frag_pct = info.total_free_bytes ?
                      (uint8_t)(100 - (100 * info.largest_free_block) / info.total_free_bytes) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(LV_MEM_CAPS_PSRAM, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif /* LV_STDLIB_CUSTOM */
