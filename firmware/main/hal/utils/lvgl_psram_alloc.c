/*
 * SPDX-License-Identifier: MIT
 */
/**
 * @brief LVGL memory in PSRAM (CONFIG_LV_USE_CUSTOM_MALLOC).
 *
 * With the C library malloc, every LVGL object, style and label (almost all under 512 bytes, see
 * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL) landed in internal RAM, the same RAM Wi-Fi and the audio pipeline live on.
 * A poker table on screen took ~5 KB more and dropped the internal minimum to 2.3 KB: Wi-Fi then lost the speech
 * packets and the robot hung in "speaking" (2026-10-09). PSRAM is preferred here; internal RAM is only a fallback.
 */
#include <lvgl.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
#include <esp_heap_caps.h>
#include <string.h>

#define LV_PSRAM_CAPS    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_INTERNAL_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void* lv_malloc_core(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, LV_PSRAM_CAPS, LV_INTERNAL_CAPS);
}

void* lv_realloc_core(void* p, size_t new_size)
{
    return heap_caps_realloc_prefer(p, new_size, 2, LV_PSRAM_CAPS, LV_INTERNAL_CAPS);
}

void lv_free_core(void* p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, LV_PSRAM_CAPS);
    memset(mon_p, 0, sizeof(*mon_p));
    mon_p->total_size        = info.total_free_bytes + info.total_allocated_bytes;
    mon_p->free_size         = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->free_cnt          = info.free_blocks;
    mon_p->used_cnt          = info.allocated_blocks;
    mon_p->max_used          = mon_p->total_size - info.minimum_free_bytes;
    mon_p->used_pct = mon_p->total_size ? (uint8_t)(100 * info.total_allocated_bytes / mon_p->total_size) : 0;
    mon_p->frag_pct =
        info.total_free_bytes ? (uint8_t)(100 - 100 * info.largest_free_block / info.total_free_bytes) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(LV_PSRAM_CAPS, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif /* LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM */
