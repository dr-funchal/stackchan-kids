/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

/**
 * @brief microSD card on the CoreS3 slot, mounted as FAT at /sdcard.
 *
 * The slot shares SPI3 with the LCD, and its MISO (GPIO35) is also the LCD D/C line. That works because
 * esp_lcd only drives D/C during its own transfers and the SPI driver serializes the two devices
 */
namespace sd_card {

inline constexpr const char* kMountPoint = "/sdcard";

/**
 * @brief Hold this around every file operation on the card (fopen/fread/fwrite/fclose/stat/opendir/...).
 *
 * The LCD streams frames over the same SPI bus with queued DMA transfers, and an SD transfer started while
 * one is in flight trips an assert in the SPI HAL. The guard takes the LVGL lock (so no new frame starts)
 * and waits for the current frame to finish. Keep the guarded section short: the screen is frozen meanwhile
 */
/**
 * @brief True if internal DMA memory can take an SD transfer right now.
 *
 * FATFS buffers live in PSRAM, so every SD transfer needs a temporary DMA-capable copy in internal RAM. When
 * that runs out the SPI driver crashed (seen during long conversations), so background writers check this first
 */
bool hasDmaHeadroom();

class BusGuard {
public:
    BusGuard();
    ~BusGuard();
    BusGuard(const BusGuard&)            = delete;
    BusGuard& operator=(const BusGuard&) = delete;
};

/**
 * @brief Mount the card. Safe to call again; returns true if mounted
 * @param formatIfUnreadable Format the card (erasing it) when it has no readable FAT filesystem
 */
bool mount(bool formatIfUnreadable = false);

bool isMounted();

/**
 * @brief First-time setup: if the StackChan folder layout is missing, format the card (FAT, erases everything)
 * and create it. Does nothing on a card that was already prepared
 */
bool prepare();

/**
 * @brief Log card type/size and the root directory listing
 */
void logInfo();

}  // namespace sd_card
