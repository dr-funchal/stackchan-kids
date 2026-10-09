/*
 * SPDX-License-Identifier: MIT
 */
#include "sd_images.h"
#include "sd_card.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

static const char* TAG = "SdImages";

static constexpr long kMaxImageBytes = 320 * 240 * 4;  // A full-screen ARGB8888 image; anything bigger is corrupt

namespace {
// Never freed: images live for the whole run. Guarded by the bus guard (the LVGL lock), which callers on the LVGL
// side already hold: a separate mutex taken in the other order could deadlock against them
std::map<std::string, lv_image_dsc_t*> _cache;
}  // namespace

static std::string path_for(const char* name)
{
    return std::string(sd_images::kDir) + "/" + name + ".bin";
}

static void make_dirs_for(const std::string& file)
{
    // mkdir -p for every folder below /sdcard/stackchan
    for (size_t pos = strlen("/sdcard/stackchan/"); pos < file.size(); pos++) {
        if (file[pos] == '/') {
            mkdir(file.substr(0, pos).c_str(), 0775);
        }
    }
}

const lv_image_dsc_t* sd_images::get(const char* name)
{
    if (!sd_card::isMounted()) {
        return nullptr;
    }
    sd_card::BusGuard guard;
    auto it = _cache.find(name);
    if (it != _cache.end()) {
        return it->second;
    }

    std::string path = path_for(name);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        ESP_LOGW(TAG, "Missing %s", path.c_str());
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    lv_image_header_t header;
    long data_size = size - (long)sizeof(header);
    uint8_t* data  = data_size > 0 && data_size <= kMaxImageBytes ? (uint8_t*)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM) : nullptr;
    bool ok        = data && fread(&header, 1, sizeof(header), f) == sizeof(header) &&
              header.magic == LV_IMAGE_HEADER_MAGIC && fread(data, 1, data_size, f) == (size_t)data_size;
    fclose(f);
    if (!ok) {
        ESP_LOGW(TAG, "Invalid image %s", path.c_str());
        heap_caps_free(data);
        return nullptr;
    }

    auto* dsc = (lv_image_dsc_t*)heap_caps_calloc(1, sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM);
    if (!dsc) {
        heap_caps_free(data);
        return nullptr;
    }
    dsc->header    = header;
    dsc->data      = data;
    dsc->data_size = data_size;
    _cache[name]   = dsc;
    return dsc;
}

bool sd_images::exists(const char* name)
{
    if (!sd_card::isMounted()) {
        return false;
    }
    sd_card::BusGuard guard;
    struct stat st = {};
    return stat(path_for(name).c_str(), &st) == 0;
}

bool sd_images::save(const char* name, const lv_image_dsc_t* image)
{
    if (!sd_card::isMounted() || !image) {
        return false;
    }
    std::string path = path_for(name);
    sd_card::BusGuard guard;
    make_dirs_for(path);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(&image->header, 1, sizeof(image->header), f) == sizeof(image->header) &&
              fwrite(image->data, 1, image->data_size, f) == image->data_size;
    fclose(f);
    if (!ok) {
        remove(path.c_str());
    }
    ESP_LOGI(TAG, "%s %s (%u bytes)", ok ? "Saved" : "Failed", path.c_str(), (unsigned)image->data_size);
    return ok;
}
