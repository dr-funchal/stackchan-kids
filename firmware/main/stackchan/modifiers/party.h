/*
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include <hal/hal.h>

namespace stackchan {

/**
 * @brief End-of-game party: rotating rainbow on the body LEDs for ~4.5 s (pair it with DanceModifier::Happy)
 */
class PartyModifier : public Modifier {
public:
    void _update(Modifiable&) override
    {
        uint32_t now = GetHAL().millis();
        if (_start == 0) {
            _start = now;
        }
        if (now - _start > 4500) {
            for (uint8_t i = 1; i < 12; i++) {
                GetHAL().setRgbColor(i, 0, 0, 0);
            }
            GetHAL().refreshRgb();
            requestDestroy();
            return;
        }
        uint32_t phase = (now - _start) / 60;
        for (uint8_t i = 1; i < 12; i++) {
            // Six-color wheel, rotating
            static const uint8_t wheel[6][3] = {{40, 0, 0}, {40, 20, 0}, {30, 30, 0},
                                                {0, 40, 0}, {0, 0, 40}, {25, 0, 35}};
            const auto& c = wheel[(i + phase) % 6];
            GetHAL().setRgbColor(i, c[0], c[1], c[2]);
        }
        GetHAL().refreshRgb();
    }

private:
    uint32_t _start = 0;
};

}  // namespace stackchan
