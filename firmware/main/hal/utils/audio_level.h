/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <esp_timer.h>
#include <atomic>
#include <cmath>
#include <cstdint>

/**
 * @brief Speaker output level, shared between the audio task (writer) and the avatar update task (reader)
 */
namespace audio_level {

inline std::atomic<uint16_t> g_output_rms{0};
inline std::atomic<int64_t> g_output_time_us{0};

/**
 * @brief Record the RMS of a PCM block about to be played. Call from the audio codec Write()
 */
inline void reportOutput(const int16_t* data, int samples)
{
    if (data == nullptr || samples <= 0) {
        return;
    }

    int64_t sum_sq = 0;
    for (int i = 0; i < samples; i++) {
        sum_sq += (int32_t)data[i] * data[i];
    }

    g_output_rms.store((uint16_t)std::sqrt((double)sum_sq / samples), std::memory_order_relaxed);
    g_output_time_us.store(esp_timer_get_time(), std::memory_order_relaxed);
}

/**
 * @brief Latest output RMS, or 0 if nothing has been played for maxAgeMs
 */
inline uint16_t getOutputRms(uint32_t maxAgeMs = 150)
{
    int64_t age_us = esp_timer_get_time() - g_output_time_us.load(std::memory_order_relaxed);
    if (age_us > (int64_t)maxAgeMs * 1000) {
        return 0;
    }
    return g_output_rms.load(std::memory_order_relaxed);
}

}  // namespace audio_level
