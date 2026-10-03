/*
 * SPDX-License-Identifier: MIT
 */
#include "sd_card.h"
#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <src/display/lv_display_private.h>
#include <esp_rom_gpio.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <soc/spi_periph.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>

static const char* TAG = "SdCard";

static constexpr spi_host_device_t kHost = SPI3_HOST;  // Shared with the LCD (initialized by the board)
static constexpr gpio_num_t kCsPin       = GPIO_NUM_4;
static constexpr gpio_num_t kMisoPin     = GPIO_NUM_35;  // Also the LCD D/C line

static sdmmc_card_t* _card = nullptr;

bool sd_card::hasDmaHeadroom()
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) >= 8 * 1024;
}

sd_card::BusGuard::BusGuard()
{
    int64_t t0 = esp_timer_get_time();
    lvgl_port_lock(0);
    int64_t t1 = esp_timer_get_time();
    // flushing is cleared from the LCD's DMA-done interrupt, so it can drop while we hold the lock
    lv_display_t* display = lv_display_get_default();
    while (display && display->flushing && esp_timer_get_time() - t1 < 200 * 1000) {
        vTaskDelay(1);
    }
    int64_t t2 = esp_timer_get_time();
    if (t2 - t0 > 300 * 1000) {
        ESP_LOGW(TAG, "BusGuard slow in %s: lock %lld ms, flush wait %lld ms (flushing=%d)",
                 pcTaskGetName(nullptr), (t1 - t0) / 1000, (t2 - t1) / 1000, display ? (int)display->flushing : -1);
    }
}

sd_card::BusGuard::~BusGuard()
{
    lvgl_port_unlock();
}

bool sd_card::mount(bool formatIfUnreadable)
{
    if (_card) {
        return true;
    }

    // Keep the card deselected so it ignores LCD traffic
    gpio_reset_pin(kCsPin);
    gpio_set_direction(kCsPin, GPIO_MODE_OUTPUT);
    gpio_set_level(kCsPin, 1);

    // The LCD bus was set up without MISO. Route GPIO35 into SPI3 MISO as an input; esp_lcd only enables its
    // output during LCD transfers, and the SPI driver never runs an LCD and an SD transfer at the same time
    gpio_input_enable(kMisoPin);
    gpio_pullup_en(kMisoPin);
    esp_rom_gpio_connect_in_signal(kMisoPin, spi_periph_signal[kHost].spiq_in, false);

    sdmmc_host_t host  = SDSPI_HOST_DEFAULT();
    host.slot          = kHost;
    host.max_freq_khz  = SDMMC_FREQ_DEFAULT;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id               = kHost;
    slot.gpio_cs               = kCsPin;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed           = formatIfUnreadable;
    mount_config.max_files                        = 5;
    mount_config.allocation_unit_size             = 16 * 1024;

    esp_err_t err;
    {
        BusGuard guard;
        err = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot, &mount_config, &_card);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(err));
        _card = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Mounted at %s", kMountPoint);
    return true;
}

// Folder layout. The marker file means the card was formatted and prepared by StackChan
static const char* kRootDir    = "/sdcard/stackchan";
static const char* kMarkerFile = "/sdcard/stackchan/.prepared";
static const char* kSubDirs[]  = {"vozes", "historias", "skins", "diario", "backup"};

static bool file_exists(const char* path)
{
    struct stat st = {};
    return stat(path, &st) == 0;
}

bool sd_card::prepare()
{
    if (!_card) {
        return false;
    }
    BusGuard guard;
    if (file_exists(kMarkerFile)) {
        return true;
    }

    ESP_LOGW(TAG, "Card not prepared yet: formatting (this erases the card)");
    esp_err_t err = esp_vfs_fat_sdcard_format(kMountPoint, _card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(err));
        return false;
    }

    mkdir(kRootDir, 0775);
    for (auto* sub : kSubDirs) {
        std::string path = std::string(kRootDir) + "/" + sub;
        if (mkdir(path.c_str(), 0775) != 0) {
            ESP_LOGE(TAG, "Cannot create %s", path.c_str());
            return false;
        }
    }

    FILE* marker = fopen(kMarkerFile, "w");
    if (!marker) {
        ESP_LOGE(TAG, "Cannot write marker");
        return false;
    }
    fputs("Prepared by StackChan firmware. Delete this file to format the card again on next boot.\n", marker);
    fclose(marker);

    ESP_LOGI(TAG, "Card formatted and prepared");
    return true;
}

bool sd_card::isMounted()
{
    return _card != nullptr;
}

void sd_card::logInfo()
{
    if (!_card) {
        ESP_LOGW(TAG, "No card mounted");
        return;
    }

    sdmmc_card_print_info(stdout, _card);
    BusGuard guard;

    uint64_t total_bytes = 0;
    uint64_t free_bytes  = 0;
    if (esp_vfs_fat_info(kMountPoint, &total_bytes, &free_bytes) == ESP_OK) {
        ESP_LOGI(TAG, "FAT: %u MB total, %u MB free", (unsigned)(total_bytes >> 20), (unsigned)(free_bytes >> 20));
    }

    DIR* dir = opendir(kMountPoint);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open %s", kMountPoint);
        return;
    }

    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr && count < 50) {
        std::string path = std::string(kMountPoint) + "/" + entry->d_name;
        struct stat st   = {};
        stat(path.c_str(), &st);
        ESP_LOGI(TAG, "  %s %s (%ld bytes)", S_ISDIR(st.st_mode) ? "[dir] " : "[file]", entry->d_name,
                 (long)st.st_size);
        count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "Root has %d entries%s", count, count >= 50 ? " (listing truncated)" : "");
}
