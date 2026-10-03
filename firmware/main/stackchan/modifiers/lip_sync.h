/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include <hal/utils/audio_level.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stackchan {

/**
 * @brief Drive the mouth from the real speaker output level (lip sync)
 */
class LipSyncModifier : public Modifier {
public:
    /**
     * @param gateRms Output RMS at or below this keeps the mouth closed (noise gate)
     * @param fullRms Output RMS that opens the mouth to maxWeight
     * @param maxWeight Max mouth weight (0~100)
     */
    LipSyncModifier(uint16_t gateRms = 200, uint16_t fullRms = 6000, int maxWeight = 85)
        : _gate_db(to_db(gateRms)), _full_db(to_db(fullRms)), _max_weight(maxWeight)
    {
    }

    void _update(Modifiable& stackchan) override
    {
        if (!stackchan.hasAvatar()) {
            return;
        }

        // Map level in dB, so quiet syllables still move the mouth
        float target = 0.0f;
        uint16_t rms = audio_level::getOutputRms();
        if (rms > 0) {
            float t = (to_db(rms) - _gate_db) / (_full_db - _gate_db);
            target  = std::clamp(t, 0.0f, 1.0f) * _max_weight;
        }

        // Open fast, close slower, so it reads as speech instead of flicker
        float k = target > _current ? _attack : _release;
        _current += (target - _current) * k;

        int weight = (int)std::lround(_current);
        if (weight != _last_weight) {
            stackchan.avatar().mouth().setWeight(weight);
            _last_weight = weight;
        }
    }

private:
    static float to_db(uint16_t rms)
    {
        return 20.0f * std::log10(std::max<float>(rms, 1.0f));
    }

    const float _gate_db;
    const float _full_db;
    const int _max_weight;
    const float _attack  = 0.7f;
    const float _release = 0.4f;

    float _current   = 0.0f;
    int _last_weight = -1;
};

}  // namespace stackchan
