/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>

/**
 * @brief Images stored on the microSD card instead of the firmware, in LVGL binary format
 * (/sdcard/stackchan/imagens/<path>.bin: lv_image_header_t followed by the pixels).
 *
 * Loaded once into PSRAM and kept for the whole run
 */
namespace sd_images {

inline constexpr const char* kDir = "/sdcard/stackchan/imagens";

// e.g. get("halloween/halloween_bg"). nullptr if the card or the file is missing
const lv_image_dsc_t* get(const char* name);

// Write an in-memory image to the card (used to move images out of the firmware)
bool save(const char* name, const lv_image_dsc_t* image);

bool exists(const char* name);

}  // namespace sd_images
